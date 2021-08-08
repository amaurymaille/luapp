function f(a)
    local b = 13
    local c = function(x)
        print ("First closure, x = " .. x .. ", a = " .. a)
        return a, function(y)
            print ("Second closure, y = " .. y .. ", x = " .. x)
            return x + y 
        end
    end
    return c
end

for i = 1, 10 do
    local f_res = f(i)
    local a, f_res2 = f_res(i + 1)
    print (a .. ", " .. f_res2(i + 2))
end
