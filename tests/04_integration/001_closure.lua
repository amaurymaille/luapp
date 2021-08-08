function f()
    local a = 12
    return function()
        print ("a is " .. a)
    end
end

f()()
