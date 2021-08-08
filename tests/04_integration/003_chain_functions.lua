function f(x)
    print ("Entering f with x = " .. x)
    print ("Leaving f with x = " .. x)
end

function g(x)
    print ("Entering g with x = " .. x)
    f(x)
    print ("Back in g with x = " .. x)
end

function h(x)
    print ("Entering h with x = " .. x)
    g(x)
    print ("Back in h with x = " .. x)
end

h(12)
